from django.db import models


class Comment(models.Model):
    owner = models.ForeignKey("accounts.MissingAuthor", on_delete=models.CASCADE)
    text = models.TextField()
